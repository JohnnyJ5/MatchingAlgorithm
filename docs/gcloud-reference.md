# Google Cloud CLI Reference

## Common Identifiers

Replace these placeholders with your actual values:

| Identifier | Description | Example |
|---|---|---|
| `PROJECT_ID` | GCP project ID | `datingapp-491722` |
| `SERVICE_NAME` | Cloud Run service name | `` |
| `REGION` | Deployment region | `` |
| `IMAGE_NAME` | Container image name | `` |
| `REPO_NAME` | Artifact Registry repo | `` |
| `INSTANCE_NAME` | Cloud SQL instance name | `` |
| `DB_NAME` | Database name | `` |
| `SA_NAME` | Service account name | `` |
| `SA_EMAIL` | Service account email | `` |
| `CLUSTER_NAME` | GKE cluster name | `` |
| `SECRET_NAME` | Secret Manager secret | `` |

---

## Auth & Config

```bash
# Login
gcloud auth login
gcloud auth application-default login

# Set active project
gcloud config set project PROJECT_ID

# View current config
gcloud config list

# Create/switch named configurations
gcloud config configurations create my-config
gcloud config configurations activate my-config
```

---

## Project Management

```bash
# List projects
gcloud projects list

# Get current project
gcloud config get-value project

# Enable an API
gcloud services enable run.googleapis.com
gcloud services enable sqladmin.googleapis.com
gcloud services enable secretmanager.googleapis.com
gcloud services enable artifactregistry.googleapis.com
```

---

## Artifact Registry (Container Images)

```bash
# Create a repository
gcloud artifacts repositories create REPO_NAME \
  --repository-format=docker \
  --location=REGION

# Configure Docker auth
gcloud auth configure-docker REGION-docker.pkg.dev

# Full image path
# REGION-docker.pkg.dev/PROJECT_ID/REPO_NAME/IMAGE_NAME:TAG

# Build and push
docker build -t REGION-docker.pkg.dev/PROJECT_ID/REPO_NAME/IMAGE_NAME:latest .
docker push REGION-docker.pkg.dev/PROJECT_ID/REPO_NAME/IMAGE_NAME:latest

# List images
gcloud artifacts docker images list REGION-docker.pkg.dev/PROJECT_ID/REPO_NAME
```

---

## Cloud Run

```bash
# Deploy a service
gcloud run deploy SERVICE_NAME \
  --image=REGION-docker.pkg.dev/PROJECT_ID/REPO_NAME/IMAGE_NAME:latest \
  --region=REGION \
  --platform=managed \
  --allow-unauthenticated \
  --port=9090 \
  --set-env-vars="DB_NAME=DB_NAME,DB_USER=app_user" \
  --set-secrets="DB_PASSWORD=SECRET_NAME:latest"

# List services
gcloud run services list --region=REGION

# Describe a service (get URL, env vars, etc.)
gcloud run services describe SERVICE_NAME --region=REGION

# Get service URL
gcloud run services describe SERVICE_NAME \
  --region=REGION \
  --format="value(status.url)"

# Delete a service
gcloud run services delete SERVICE_NAME --region=REGION

# View logs
gcloud logging read "resource.type=cloud_run_revision AND resource.labels.service_name=SERVICE_NAME" \
  --limit=50 --format="table(timestamp,textPayload)"

# Stream logs (tail)
gcloud beta run services logs tail SERVICE_NAME --region=REGION
```

---

## Cloud SQL (PostgreSQL)

```bash
# Create a PostgreSQL 16 instance
gcloud sql instances create INSTANCE_NAME \
  --database-version=POSTGRES_16 \
  --tier=db-f1-micro \
  --region=REGION

# List instances
gcloud sql instances list

# Describe instance (get connection name)
gcloud sql instances describe INSTANCE_NAME

# Create a database
gcloud sql databases create DB_NAME --instance=INSTANCE_NAME

# Create a user
gcloud sql users create app_user \
  --instance=INSTANCE_NAME \
  --password=your_password

# Connect via Cloud SQL Auth Proxy (local)
cloud-sql-proxy PROJECT_ID:REGION:INSTANCE_NAME

# Connection name format (used in app config)
# PROJECT_ID:REGION:INSTANCE_NAME
```

---

## Secret Manager

```bash
# Create a secret
echo -n "my_secret_value" | gcloud secrets create SECRET_NAME --data-file=-

# Add a new version
echo -n "new_value" | gcloud secrets versions add SECRET_NAME --data-file=-

# Access latest version
gcloud secrets versions access latest --secret=SECRET_NAME

# List secrets
gcloud secrets list

# Grant Cloud Run service account access
gcloud secrets add-iam-policy-binding SECRET_NAME \
  --member="serviceAccount:SA_EMAIL" \
  --role="roles/secretmanager.secretAccessor"
```

---

## IAM & Service Accounts

```bash
# Create a service account
gcloud iam service-accounts create SA_NAME \
  --display-name="Dating App Service Account"

# SA email format: SA_NAME@PROJECT_ID.iam.gserviceaccount.com

# Grant a role to a service account
gcloud projects add-iam-policy-binding PROJECT_ID \
  --member="serviceAccount:SA_EMAIL" \
  --role="roles/cloudsql.client"

# Common roles:
#   roles/cloudsql.client          — connect to Cloud SQL
#   roles/secretmanager.secretAccessor — read secrets
#   roles/run.invoker              — invoke Cloud Run
#   roles/artifactregistry.reader  — pull images

# Create and download a key (avoid if possible; prefer Workload Identity)
gcloud iam service-accounts keys create key.json \
  --iam-account=SA_EMAIL

# List service accounts
gcloud iam service-accounts list
```

---

## Cloud Build (CI/CD)

```bash
# Submit a build manually (uses cloudbuild.yaml)
gcloud builds submit --config=cloudbuild.yaml .

# Trigger a build from source
gcloud builds submit \
  --tag=REGION-docker.pkg.dev/PROJECT_ID/REPO_NAME/IMAGE_NAME:latest .

# List recent builds
gcloud builds list --limit=10

# View build logs
gcloud builds log BUILD_ID
```

---

## Useful Flags

| Flag | Description |
|---|---|
| `--project=PROJECT_ID` | Override active project for one command |
| `--region=REGION` | Specify region |
| `--format=json` | Output as JSON |
| `--format=yaml` | Output as YAML |
| `--format="value(field)"` | Extract a single field value |
| `--quiet` / `-q` | Skip confirmation prompts |
| `--verbosity=debug` | Verbose output for troubleshooting |

---

## Quick Reference: This Project

Run this block to auto-populate what gcloud knows, then fill in the names you define:

```bash
# --- Auto-discovered from gcloud config / existing resources ---
export PROJECT_ID=$(gcloud config get-value project 2>/dev/null)
export REGION=$(gcloud config get-value run/region 2>/dev/null || echo "us-central1")
export ACCOUNT=$(gcloud config get-value account 2>/dev/null)

# Check what Cloud Run services already exist in this project
gcloud run services list --region=$REGION --format="value(metadata.name)"

# Check what Cloud SQL instances exist
gcloud sql instances list --format="value(name)"

# Check what Artifact Registry repos exist
gcloud artifacts repositories list --location=$REGION --format="value(name)"

# Check what service accounts exist
gcloud iam service-accounts list --format="value(email)"

# Check what secrets exist
gcloud secrets list --format="value(name)"

# --- Names you define (set these manually) ---
export SERVICE_NAME=dating-app-server
export REPO_NAME=dating-app
export IMAGE_NAME=server
export INSTANCE_NAME=dating-app-db
export DB_NAME=datingapp
export SA_NAME=dating-app-sa

# --- Derived (auto-computed once the above are set) ---
export SA_EMAIL="${SA_NAME}@${PROJECT_ID}.iam.gserviceaccount.com"
export IMAGE_URI="${REGION}-docker.pkg.dev/${PROJECT_ID}/${REPO_NAME}/${IMAGE_NAME}:latest"
export SQL_CONNECTION="${PROJECT_ID}:${REGION}:${INSTANCE_NAME}"

# Verify
echo "Project:    $PROJECT_ID"
echo "Region:     $REGION"
echo "Service:    $SERVICE_NAME"
echo "Image:      $IMAGE_URI"
echo "DB conn:    $SQL_CONNECTION"
echo "SA:         $SA_EMAIL"
```
